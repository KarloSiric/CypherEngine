//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Tests VFS-backed cooked material adoption and GPU ownership.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/Runtime/CypherRender_MaterialBridge.h"
#include "CypherRender/CypherRender_Local.h"
#include "CypherResource/CypherResource_RenderAssets.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <initializer_list>
#include <vector>

namespace common = ::cypher::common;
namespace render = ::cypher::engine::render;
namespace resource = ::cypher::engine::resource;

namespace
{

template <common::usize nLength>
constexpr common::string_view_t Text( const char ( &text )[nLength] ) noexcept
{
    return { text, nLength - 1u };
}

constexpr auto kMaterialPath = Text( "materials/bridge.cymat_c" );
constexpr auto kShaderPath = Text( "shaders/bridge.cyshader_c" );
constexpr auto kAlbedoPath = Text( "textures/albedo.cytex_c" );
constexpr auto kDetailPath = Text( "textures/detail.cytex_c" );

struct memory_file_t {
    common::string_view_t path{};
    common::binary_block_t contents{};
};

struct memory_vfs_t {
    memory_file_t files[4]{};
    common::u32 fileCount{ 0u };
    common::u32 reads{ 0u };
};

common::vfs_status_t ReadFile(
    void *pUserData, common::string_view_t path, common::usize maximum,
    common::blob_t *pDest ) noexcept
{
    auto &storage = *static_cast<memory_vfs_t *>( pUserData );
    for ( common::u32 index = 0u; index < storage.fileCount; ++index ) {
        const auto &file = storage.files[index];
        if ( !common::StringView_Equals( path, file.path ) ) continue;
        ++storage.reads;
        if ( file.contents.cbSize > maximum ) return common::vfs_status_t::SIZE_LIMIT;
        return common::Blob_Assign( pDest, file.contents )
            ? common::vfs_status_t::OK : common::vfs_status_t::OUT_OF_MEMORY;
    }
    return common::vfs_status_t::NOT_FOUND;
}

common::u64 BindingId( common::string_view_t name )
{
    common::u64 id = 0u;
    REQUIRE( common::CookedShader_MakeLogicalBindingId( name, &id ) );
    return id;
}

struct cooked_files_t {
    std::vector<common::byte> material{};
    std::vector<common::byte> shader{};
    std::vector<common::byte> albedo{};
    std::vector<common::byte> detail{};
    common::content_hash_t interfaceHash{};

    void Build( common::u32 textureCount, bool mismatch = false,
        bool withFeature = false, bool withSampler = false,
        common::render_material_alpha_mode_t alphaMode =
            common::render_material_alpha_mode_t::OPAQUE,
        common::f64 maskCutoff = 0.375 )
    {
        REQUIRE( textureCount <= 2u );

        common::cooked_shader_binding_source_t bindings[2]{};
        bindings[0].name = Text( "AlbedoMap" );
        bindings[1].name = Text( "DetailMap" );
        for ( common::u32 index = 0u; index < textureCount; ++index ) {
            auto &binding = bindings[index];
            binding.kind = common::render_shader_binding_kind_t::SAMPLED_TEXTURE;
            binding.resourceType = common::render_shader_resource_type_t::TEXTURE_2D;
            binding.stageMask = common::RENDER_SHADER_STAGE_MASK_FRAGMENT;
            binding.flags = common::COOKED_SHADER_BINDING_FLAG_REQUIRED |
                common::COOKED_SHADER_BINDING_FLAG_MATERIAL;
            binding.nLogicalBinding = BindingId( binding.name );
        }
        const common::cooked_shader_interface_source_t shaderInterface{
            { bindings, textureCount }
        };
        REQUIRE( common::CookedShader_ComputeInterfaceHash(
            shaderInterface, &interfaceHash ) );

        constexpr char vertex[] =
            "#version 410 core\nvoid main(){gl_Position=vec4(0.0);}\n";
        constexpr char fragment[] =
            "#version 410 core\nout vec4 c;void main(){c=vec4(1.0);}\n";
        common::cooked_shader_stage_source_t stages[2]{};
        stages[0].stage = common::render_shader_stage_t::VERTEX;
        stages[0].code = { reinterpret_cast<const common::byte *>( vertex ), sizeof( vertex ) };
        stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        stages[1].code = { reinterpret_cast<const common::byte *>( fragment ), sizeof( fragment ) };
        const auto shaderStages = common::span_t<const common::cooked_shader_stage_source_t>{
            stages, 2u
        };
        shader.resize( common::CookedShader_RequiredSizeV3(
            {}, shaderStages, shaderInterface ) );
        REQUIRE( !shader.empty() );
        REQUIRE( common::CookedShader_Succeeded( common::CookedShader_WriteV3(
            {}, shaderStages, shaderInterface,
            common::ContentHash_String( Text( "shaders/bridge.cyshader" ) ),
            { shader.data(), shader.size() } ) ) );

        constexpr common::byte albedoPixels[4]{ 0xDAu, 0x10u, 0x20u, 0xFFu };
        constexpr common::byte detailPixels[4]{ 0x13u, 0x42u, 0x71u, 0xFFu };
        MakeTexture( albedo, Text( "textures/albedo.cytex" ), albedoPixels );
        MakeTexture( detail, Text( "textures/detail.cytex" ), detailPixels );

        common::cooked_material_texture_source_v2_t textures[2]{};
        textures[0].binding = bindings[0].name;
        textures[0].texture = Text( "textures/albedo.cytex" );
        textures[0].nLogicalBinding = bindings[0].nLogicalBinding;
        textures[1].binding = bindings[1].name;
        textures[1].texture = Text( "textures/detail.cytex" );
        textures[1].nLogicalBinding = BindingId( bindings[1].name );
        if ( withSampler ) {
            textures[0].sampler = Text( "linear_wrap" );
            textures[0].bHasSampler = common::CY_TRUE;
        }
        common::cooked_material_feature_source_v2_t feature{};
        feature.name = Text( "DetailFeature" );
        feature.type = common::cooked_material_feature_value_type_t::BOOL;
        feature.bValue = common::CY_TRUE;
        common::cooked_material_source_v2_t description{};
        description.shader = Text( "shaders/bridge.cyshader" );
        description.shaderInterfaceHash = interfaceHash;
        if ( mismatch ) description.shaderInterfaceHash.low ^= 1u;
        description.textures = { textures, textureCount };
        if ( withFeature ) description.features = { &feature, 1u };
        description.alphaMode = alphaMode;
        if ( alphaMode == common::render_material_alpha_mode_t::MASK ) {
            description.alphaCutoff = maskCutoff;
        }
        description.flags = common::COOKED_MATERIAL_FLAG_NONE;
        material.resize( common::CookedMaterial_RequiredSizeV2( description ) );
        REQUIRE( !material.empty() );
        REQUIRE( common::CookedMaterial_Succeeded( common::CookedMaterial_WriteV2(
            description,
            common::ContentHash_String( Text( "materials/bridge.cymat" ) ),
            { material.data(), material.size() } ) ) );
    }

    static void MakeTexture( std::vector<common::byte> &file,
        common::string_view_t name, const common::byte ( &pixels )[4] )
    {
        common::cooked_texture_desc_t description{};
        description.nWidth = 1u;
        description.nHeight = 1u;
        description.nMipLevels = 1u;
        const common::cooked_texture_mip_source_t mip{
            1u, 1u, 1u, 4u, { pixels, sizeof( pixels ) }
        };
        file.resize( common::CookedTexture_RequiredSize(
            description, { &mip, 1u } ) );
        REQUIRE( !file.empty() );
        REQUIRE( common::CookedTexture_Succeeded( common::CookedTexture_Write(
            description, { &mip, 1u }, common::ContentHash_String( name ),
            { file.data(), file.size() } ) ) );
    }
};

struct bridge_fixture_t {
    cooked_files_t cooked{};
    memory_vfs_t storage{};
    common::vfs_t vfs{};
    resource::render_asset_loader_context_t loader{};
    resource::resource_manager_t resources{};
    render::renderer_state_t savedRenderer{ render::tr };
    render::backend_api_t backend{};
    render::render_material_bundle_t bundle{};
    common::u64 nextToken{ 1u };
    common::u32 shaderCreates{ 0u }, shaderDestroys{ 0u };
    common::u32 pipelineCreates{ 0u }, pipelineDestroys{ 0u };
    common::u32 textureCreates{ 0u }, textureDestroys{ 0u };
    common::u32 failTextureAt{ 0u };
    bool tokenOnTextureFailure{ false };
    bool failNextTextureDestroy{ false };
    render::render_error_t pipelineResult{ render::render_error_t::OK };
    common::u32 lastPipelineTextureCount{ 0u };
    bool lastPipelineAlphaBlend{ false };
    bool lastPipelineDepthWrite{ true };
    bool pipelineTextureNamesMatch{ false };
    common::byte uploadedPixels[2][4]{};

    void Init( common::u32 textureCount, bool mismatch = false,
        bool withFeature = false, bool withSampler = false,
        common::render_material_alpha_mode_t alphaMode =
            common::render_material_alpha_mode_t::OPAQUE,
        common::f64 maskCutoff = 0.375 )
    {
        cooked.Build( textureCount, mismatch, withFeature, withSampler,
            alphaMode, maskCutoff );
        storage.files[0] = { kMaterialPath, { cooked.material.data(), cooked.material.size() } };
        storage.files[1] = { kShaderPath, { cooked.shader.data(), cooked.shader.size() } };
        storage.files[2] = { kAlbedoPath, { cooked.albedo.data(), cooked.albedo.size() } };
        storage.files[3] = { kDetailPath, { cooked.detail.data(), cooked.detail.size() } };
        storage.fileCount = 2u + textureCount;
        static const common::vfs_ops_t ops{ ReadFile, nullptr, nullptr, nullptr };
        vfs = { &ops, &storage, common::VFS_CAPABILITY_READ_ALL };
        REQUIRE( resource::Res_InitRenderAssetLoader(
            &loader, resource::Res_DefaultRenderAssetLoaderConfig( &vfs ) ) );
        auto config = resource::Res_DefaultConfig();
        config.cResourceCapacity = 8u;
        config.cTypeCapacity = 3u;
        REQUIRE( resource::Res_Init( &resources, config ) == resource::resource_error_t::OK );
        REQUIRE( resource::Res_RegisterRenderAssetLoaders(
            &resources, &loader ) == resource::resource_error_t::OK );

        backend.CreateShader = []( const render::render_shader_desc_t &,
            render::backend_shader_t &out, void *state ) noexcept {
            auto &fixture = *static_cast<bridge_fixture_t *>( state );
            ++fixture.shaderCreates;
            out.value = fixture.nextToken++;
            return render::render_error_t::OK;
        };
        backend.DestroyShader = []( render::backend_shader_t, void *state ) noexcept {
            ++static_cast<bridge_fixture_t *>( state )->shaderDestroys;
            return render::render_error_t::OK;
        };
        backend.CreateGraphicsPipeline = []( const render::backend_pipeline_desc_t &description,
            render::backend_pipeline_t &out, void *state ) noexcept {
            auto &fixture = *static_cast<bridge_fixture_t *>( state );
            ++fixture.pipelineCreates;
            fixture.lastPipelineTextureCount = description.sampledTextureCount;
            fixture.lastPipelineAlphaBlend = description.alphaBlend;
            fixture.lastPipelineDepthWrite = description.depthWrite;
            fixture.pipelineTextureNamesMatch = description.sampledTextureCount == 0u ||
                ( description.sampledTextures[0].name != nullptr &&
                  std::strcmp( description.sampledTextures[0].name, "AlbedoMap" ) == 0 &&
                  ( description.sampledTextureCount == 1u ||
                    ( description.sampledTextures[1].name != nullptr &&
                      std::strcmp( description.sampledTextures[1].name, "DetailMap" ) == 0 ) ) );
            out = {};
            if ( fixture.pipelineResult == render::render_error_t::OK ) {
                out.value = fixture.nextToken++;
            }
            return fixture.pipelineResult;
        };
        backend.DestroyGraphicsPipeline = []( render::backend_pipeline_t, void *state ) noexcept {
            ++static_cast<bridge_fixture_t *>( state )->pipelineDestroys;
            return render::render_error_t::OK;
        };
        backend.CreateTexture2D = []( const render::render_texture_desc_t &,
            const render::render_texture_data_t &data,
            render::backend_texture_t &out, void *state ) noexcept {
            auto &fixture = *static_cast<bridge_fixture_t *>( state );
            ++fixture.textureCreates;
            out = {};
            if ( data.bytes == nullptr || data.byteSize != 4u ) {
                return render::render_error_t::ERR_INVALID_ARGUMENT;
            }
            if ( fixture.textureCreates <= 2u ) {
                std::memcpy( fixture.uploadedPixels[fixture.textureCreates - 1u],
                    data.bytes, 4u );
            }
            if ( fixture.failTextureAt == fixture.textureCreates ) {
                if ( fixture.tokenOnTextureFailure ) out.value = fixture.nextToken++;
                return render::render_error_t::ERR_OUT_OF_MEMORY;
            }
            out.value = fixture.nextToken++;
            return render::render_error_t::OK;
        };
        backend.DestroyTexture = []( render::backend_texture_t, void *state ) noexcept {
            auto &fixture = *static_cast<bridge_fixture_t *>( state );
            ++fixture.textureDestroys;
            if ( fixture.failNextTextureDestroy ) {
                fixture.failNextTextureDestroy = false;
                return render::render_error_t::ERR_DEVICE_LOST;
            }
            return render::render_error_t::OK;
        };
        render::tr = {};
        render::tr.backend = { &backend, this };
        render::tr.initialized = true;
        render::tr.info.backend = render::render_backend_t::OPENGL;
        render::tr.info.apiMajorVersion = 4u;
        render::tr.info.apiMinorVersion = 1u;
        render::tr.info.limits.maxTexture2DSize = 4096u;
        render::tr.info.limits.maxCombinedTextureUnits = 16u;
        render::tr.info.limits.maxUniformBlockBytes = 16384u;
        render::tr.info.limits.maxUniformBufferBindings = 16u;
        REQUIRE( render::R_ShaderSystemInit() == render::render_error_t::OK );
        REQUIRE( render::R_TextureSystemInit() == render::render_error_t::OK );
        REQUIRE( render::R_PipelineSystemInit() == render::render_error_t::OK );
    }

    render::render_material_bundle_desc_t Description()
    {
        render::render_material_bundle_desc_t description{};
        description.pResources = &resources;
        description.cookedMaterialPath = kMaterialPath;
        description.vertexLayout.bindingCount = 1u;
        description.vertexLayout.bindings[0].stride = 12u;
        description.vertexLayout.attributeCount = 1u;
        description.vertexLayout.attributes[0] = {
            render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u
        };
        return description;
    }

    ~bridge_fixture_t()
    {
        (void)render::R_DestroyCookedMaterialBundle( &bundle );
        (void)render::R_PipelineSystemShutdown();
        (void)render::R_TextureSystemShutdown();
        (void)render::R_ShaderSystemShutdown();
        render::tr = savedRenderer;
        if ( resource::Res_IsInitialized( &resources ) ) {
            CHECK( resource::Res_Shutdown( &resources ) == resource::resource_error_t::OK );
        }
    }
};

} // namespace

TEST_CASE( "cooked material bridge owns zero to two textures after VFS views expire",
    "[CypherRender][MaterialBridge]" )
{
    for ( common::u32 textureCount : { 0u, 1u, 2u } ) {
        bridge_fixture_t fixture{};
        fixture.Init( textureCount );
        const auto description = fixture.Description();
        const auto result = render::R_CreateCookedMaterialBundle(
            description, &fixture.bundle );
        CHECK( result.status == render::render_material_bridge_status_t::OK );
        CHECK( fixture.storage.reads == textureCount + 2u );
        CHECK( fixture.shaderCreates == 1u );
        CHECK( fixture.pipelineCreates == 1u );
        CHECK( fixture.textureCreates == textureCount );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
        CHECK( fixture.lastPipelineTextureCount == textureCount );
        CHECK_FALSE( fixture.lastPipelineAlphaBlend );
        CHECK( fixture.lastPipelineDepthWrite );
        CHECK( fixture.pipelineTextureNamesMatch );
        CHECK( fixture.bundle.textureCount == textureCount );
        CHECK( fixture.bundle.alphaMode == common::render_material_alpha_mode_t::OPAQUE );
        CHECK( fixture.bundle.alphaCutoff == 0.5F );
        CHECK( render::R_IsShaderValid( fixture.bundle.shader ) );
        CHECK( render::R_IsGraphicsPipelineValid( fixture.bundle.pipeline ) );
        for ( common::u32 index = 0u; index < textureCount; ++index ) {
            CHECK( render::R_IsTextureValid( fixture.bundle.textures[index] ) );
        }
        if ( textureCount > 0u ) CHECK( fixture.uploadedPixels[0][0] == 0xDAu );
        if ( textureCount == 2u ) CHECK( fixture.uploadedPixels[1][0] == 0x13u );

        // The bundle's GPU handles and copied shader identity must not borrow
        // VFS bytes after all resource handles have been released.
        std::memset( fixture.cooked.material.data(), 0, fixture.cooked.material.size() );
        std::memset( fixture.cooked.shader.data(), 0, fixture.cooked.shader.size() );
        std::memset( fixture.cooked.albedo.data(), 0, fixture.cooked.albedo.size() );
        std::memset( fixture.cooked.detail.data(), 0, fixture.cooked.detail.size() );
        render::render_shader_info_t shaderInfo{};
        REQUIRE( render::R_GetShaderInfo( fixture.bundle.shader, &shaderInfo ) ==
            render::render_error_t::OK );
        CHECK( common::ContentHash_Equals(
            shaderInfo.interfaceHash, fixture.cooked.interfaceHash ) );
        render::render_draw_indexed_desc_t draw{};
        REQUIRE( render::R_ApplyCookedMaterialBundleToDraw(
            fixture.bundle, &draw ) == render::render_error_t::OK );
        CHECK( draw.pipeline.value == fixture.bundle.pipeline.value );
        for ( common::u32 index = 0u; index < textureCount; ++index ) {
            CHECK( draw.sampledTextures[index].value == fixture.bundle.textures[index].value );
        }
        REQUIRE( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
            render::render_error_t::OK );
        CHECK( fixture.pipelineDestroys == 1u );
        CHECK( fixture.shaderDestroys == 1u );
        CHECK( fixture.textureDestroys == textureCount );
    }
}

TEST_CASE( "cooked blend material adopts depth-tested straight-alpha pipeline state",
    "[CypherRender][MaterialBridge]" )
{
    bridge_fixture_t fixture{};
    fixture.Init( 1u, false, false, false,
        common::render_material_alpha_mode_t::BLEND );
    const auto result = render::R_CreateCookedMaterialBundle(
        fixture.Description(), &fixture.bundle );
    REQUIRE( result.status == render::render_material_bridge_status_t::OK );
    CHECK( fixture.bundle.alphaMode == common::render_material_alpha_mode_t::BLEND );
    CHECK( fixture.bundle.alphaCutoff == 0.5F );
    CHECK( fixture.bundle.materialFlags == common::COOKED_MATERIAL_FLAG_NONE );
    CHECK( fixture.lastPipelineAlphaBlend );
    CHECK_FALSE( fixture.lastPipelineDepthWrite );
    render::render_pipeline_info_t pipelineInfo{};
    REQUIRE( render::R_GetGraphicsPipelineInfo( fixture.bundle.pipeline,
        &pipelineInfo ) == render::render_error_t::OK );
    CHECK( pipelineInfo.depthTest );
    CHECK_FALSE( pipelineInfo.depthWrite );
    CHECK( pipelineInfo.alphaBlend );
    CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
    std::memset( fixture.cooked.material.data(), 0, fixture.cooked.material.size() );
    CHECK( fixture.bundle.alphaMode == common::render_material_alpha_mode_t::BLEND );
    REQUIRE( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
        render::render_error_t::OK );
    CHECK( fixture.shaderDestroys == 1u );
    CHECK( fixture.pipelineDestroys == 1u );
    CHECK( fixture.textureDestroys == 1u );
    CHECK( fixture.bundle.alphaMode == common::render_material_alpha_mode_t::OPAQUE );
    CHECK( fixture.bundle.alphaCutoff == 0.5F );
}

TEST_CASE( "cooked mask material retains cutoff and opaque depth state",
    "[CypherRender][MaterialBridge]" )
{
    for ( const common::f64 cutoff : { 0.0, 0.375, 1.0 } ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u, false, false, false,
            common::render_material_alpha_mode_t::MASK, cutoff );
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        REQUIRE( result.status == render::render_material_bridge_status_t::OK );
        CHECK( fixture.bundle.alphaMode == common::render_material_alpha_mode_t::MASK );
        CHECK( fixture.bundle.alphaCutoff == static_cast<common::f32>( cutoff ) );
        CHECK( fixture.bundle.textureCount == 1u );
        CHECK( fixture.shaderCreates == 1u );
        CHECK( fixture.textureCreates == 1u );
        CHECK( fixture.pipelineCreates == 1u );
        CHECK_FALSE( fixture.lastPipelineAlphaBlend );
        CHECK( fixture.lastPipelineDepthWrite );
        render::render_pipeline_info_t pipelineInfo{};
        REQUIRE( render::R_GetGraphicsPipelineInfo( fixture.bundle.pipeline,
            &pipelineInfo ) == render::render_error_t::OK );
        CHECK( pipelineInfo.depthTest );
        CHECK( pipelineInfo.depthWrite );
        CHECK_FALSE( pipelineInfo.alphaBlend );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );

        std::memset( fixture.cooked.material.data(), 0, fixture.cooked.material.size() );
        CHECK( fixture.bundle.alphaCutoff == static_cast<common::f32>( cutoff ) );
        render::render_draw_indexed_desc_t draw{};
        REQUIRE( render::R_ApplyCookedMaterialBundleToDraw(
            fixture.bundle, &draw ) == render::render_error_t::OK );
        CHECK( draw.pipeline.value == fixture.bundle.pipeline.value );
        CHECK( draw.sampledTextures[0].value == fixture.bundle.textures[0].value );

        REQUIRE( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
            render::render_error_t::OK );
        CHECK( fixture.pipelineDestroys == 1u );
        CHECK( fixture.shaderDestroys == 1u );
        CHECK( fixture.textureDestroys == 1u );
        CHECK( fixture.bundle.alphaMode == common::render_material_alpha_mode_t::OPAQUE );
        CHECK( fixture.bundle.alphaCutoff == 0.5F );
    }
}

TEST_CASE( "cooked material bridge rejects mismatched and unsupported interfaces before GPU creation",
    "[CypherRender][MaterialBridge]" )
{
    SECTION( "shader interface hash differs" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u, true );
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        CHECK( result.status ==
            render::render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.pipelineCreates == 0u );
        CHECK( fixture.textureCreates == 0u );
        CHECK( fixture.bundle.shader.value == 0u );
    }
    SECTION( "feature variants are not silently dropped" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u, false, true, false,
            common::render_material_alpha_mode_t::MASK );
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        CHECK( result.status ==
            render::render_material_bridge_status_t::MATERIAL_UNSUPPORTED );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.textureCreates == 0u );
    }
    SECTION( "authored sampler preset requires a future binding path" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u, false, false, true,
            common::render_material_alpha_mode_t::MASK );
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        CHECK( result.status ==
            render::render_material_bridge_status_t::MATERIAL_UNSUPPORTED );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.textureCreates == 0u );
    }
    SECTION( "additive state is not silently treated as blending" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u, false, false, false,
            common::render_material_alpha_mode_t::ADDITIVE );
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        CHECK( result.status ==
            render::render_material_bridge_status_t::MATERIAL_UNSUPPORTED );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.pipelineCreates == 0u );
        CHECK( fixture.textureCreates == 0u );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
    }
    SECTION( "missing second texture leaves no GPU or CPU resource live" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 2u );
        fixture.storage.fileCount = 3u;
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        CHECK( result.status ==
            render::render_material_bridge_status_t::RESOURCE_ERROR );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.pipelineCreates == 0u );
        CHECK( fixture.textureCreates == 0u );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
    }
    SECTION( "uniform block cannot alias a sampled texture binding" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u );
        auto description = fixture.Description();
        description.uniformBlockCount = 1u;
        description.uniformBlocks[0] = { "AlbedoMap", 16u };
        const auto result = render::R_CreateCookedMaterialBundle(
            description, &fixture.bundle );
        CHECK( result.status ==
            render::render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.textureCreates == 0u );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
    }
}

TEST_CASE( "cooked material bridge rolls back partially created GPU bundles",
    "[CypherRender][MaterialBridge]" )
{
    SECTION( "second texture reports GPU memory failure and returns a token" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 2u );
        fixture.failTextureAt = 2u;
        fixture.tokenOnTextureFailure = true;
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        CHECK( result.status ==
            render::render_material_bridge_status_t::RENDER_ERROR );
        CHECK( result.renderError == render::render_error_t::ERR_OUT_OF_MEMORY );
        CHECK( fixture.textureCreates == 2u );
        CHECK( fixture.textureDestroys == 2u ); // Partial token and first live texture.
        CHECK( fixture.shaderDestroys == 1u );
        CHECK( fixture.pipelineDestroys == 0u );
        CHECK( fixture.bundle.shader.value == 0u );
        CHECK( fixture.bundle.pipeline.value == 0u );
        CHECK( fixture.bundle.textureCount == 0u );
    }
    SECTION( "mask pipeline failure releases both completed textures and shader" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 2u, false, false, false,
            common::render_material_alpha_mode_t::MASK );
        fixture.pipelineResult = render::render_error_t::ERR_PIPELINE_CREATE_FAILED;
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        CHECK( result.status ==
            render::render_material_bridge_status_t::RENDER_ERROR );
        CHECK( result.renderError == render::render_error_t::ERR_PIPELINE_CREATE_FAILED );
        CHECK( fixture.shaderCreates == 1u );
        CHECK( fixture.shaderDestroys == 1u );
        CHECK( fixture.textureCreates == 2u );
        CHECK( fixture.textureDestroys == 2u );
        CHECK( fixture.pipelineCreates == 1u );
        CHECK( fixture.pipelineDestroys == 0u );
        CHECK( fixture.bundle.shader.value == 0u );
        CHECK( fixture.bundle.pipeline.value == 0u );
        CHECK( fixture.bundle.textureCount == 0u );
        CHECK( fixture.bundle.alphaMode == common::render_material_alpha_mode_t::OPAQUE );
        CHECK( fixture.bundle.alphaCutoff == 0.5F );
    }
    SECTION( "mask cleanup failure retains cutoff and a retryable live handle" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 2u, false, false, false,
            common::render_material_alpha_mode_t::MASK );
        fixture.pipelineResult = render::render_error_t::ERR_PIPELINE_CREATE_FAILED;
        fixture.failNextTextureDestroy = true;
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        CHECK( result.status == render::render_material_bridge_status_t::CLEANUP_FAILED );
        CHECK( result.renderError == render::render_error_t::ERR_PIPELINE_CREATE_FAILED );
        CHECK( result.cleanupRenderError == render::render_error_t::ERR_DEVICE_LOST );
        CHECK( fixture.bundle.textureCount > 0u );
        CHECK( fixture.bundle.alphaMode == common::render_material_alpha_mode_t::MASK );
        CHECK( fixture.bundle.alphaCutoff == 0.375F );
        bool foundLiveTexture = false;
        for ( const auto texture : fixture.bundle.textures ) {
            foundLiveTexture |= render::R_IsTextureValid( texture );
        }
        CHECK( foundLiveTexture );
        REQUIRE( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
            render::render_error_t::OK );
        CHECK( fixture.bundle.textureCount == 0u );
        CHECK( fixture.bundle.alphaMode == common::render_material_alpha_mode_t::OPAQUE );
        CHECK( fixture.bundle.alphaCutoff == 0.5F );
    }
}
