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
        common::f64 maskCutoff = 0.375,
        common::flags32_t materialFlags = common::COOKED_MATERIAL_FLAG_NONE )
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
        description.flags = materialFlags;
        material.resize( common::CookedMaterial_RequiredSizeV2( description ) );
        REQUIRE( !material.empty() );
        REQUIRE( common::CookedMaterial_Succeeded( common::CookedMaterial_WriteV2(
            description,
            common::ContentHash_String( Text( "materials/bridge.cymat" ) ),
            { material.data(), material.size() } ) ) );
    }

    void BuildPassShadow( bool passMarkedMaterial = false,
        bool albedoMarkedMaterial = true )
    {
        common::cooked_shader_binding_source_t bindings[2]{};
        bindings[0].name = Text( "AlbedoMap" );
        bindings[1].name = Text( "ShadowMap" );
        for ( auto &binding : bindings ) {
            binding.kind = common::render_shader_binding_kind_t::SAMPLED_TEXTURE;
            binding.resourceType = common::render_shader_resource_type_t::TEXTURE_2D;
            binding.stageMask = common::RENDER_SHADER_STAGE_MASK_FRAGMENT;
            binding.flags = common::COOKED_SHADER_BINDING_FLAG_REQUIRED;
            binding.nLogicalBinding = BindingId( binding.name );
        }
        if ( albedoMarkedMaterial ) {
            bindings[0].flags |= common::COOKED_SHADER_BINDING_FLAG_MATERIAL;
        }
        if ( passMarkedMaterial ) {
            bindings[1].flags |= common::COOKED_SHADER_BINDING_FLAG_MATERIAL;
        }
        const common::cooked_shader_interface_source_t shaderInterface{
            { bindings, 2u }
        };
        REQUIRE( common::CookedShader_ComputeInterfaceHash(
            shaderInterface, &interfaceHash ) );
        constexpr char vertex[] =
            "#version 410 core\nvoid main(){gl_Position=vec4(0.0);}\n";
        constexpr char fragment[] =
            "#version 410 core\n"
            "uniform sampler2D AlbedoMap; uniform sampler2DShadow ShadowMap;\n"
            "out vec4 c;void main(){c=texture(AlbedoMap,vec2(0.5))"
            "*texture(ShadowMap,vec3(0.5));}\n";
        common::cooked_shader_stage_source_t stages[2]{};
        stages[0].stage = common::render_shader_stage_t::VERTEX;
        stages[0].code = { reinterpret_cast<const common::byte *>( vertex ), sizeof( vertex ) };
        stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        stages[1].code = { reinterpret_cast<const common::byte *>( fragment ), sizeof( fragment ) };
        shader.resize( common::CookedShader_RequiredSizeV3(
            {}, { stages, 2u }, shaderInterface ) );
        REQUIRE_FALSE( shader.empty() );
        REQUIRE( common::CookedShader_Succeeded( common::CookedShader_WriteV3(
            {}, { stages, 2u }, shaderInterface,
            common::ContentHash_String( Text( "shaders/bridge.cyshader" ) ),
            { shader.data(), shader.size() } ) ) );

        common::cooked_material_texture_source_v2_t albedoBinding{};
        albedoBinding.binding = bindings[0].name;
        albedoBinding.texture = Text( "textures/albedo.cytex" );
        albedoBinding.nLogicalBinding = bindings[0].nLogicalBinding;
        common::cooked_material_source_v2_t description{};
        description.shader = Text( "shaders/bridge.cyshader" );
        description.shaderInterfaceHash = interfaceHash;
        description.textures = { &albedoBinding, 1u };
        material.resize( common::CookedMaterial_RequiredSizeV2( description ) );
        REQUIRE_FALSE( material.empty() );
        REQUIRE( common::CookedMaterial_Succeeded( common::CookedMaterial_WriteV2(
            description,
            common::ContentHash_String( Text( "materials/bridge.cymat" ) ),
            { material.data(), material.size() } ) ) );
    }

    void BuildTint( common::string_view_t shaderValueName = Text( "tint" ),
        common::render_shader_value_type_t shaderValueType =
            common::render_shader_value_type_t::F32X4,
        common::u32 materialBlockBytes = 16u,
        common::u32 modelBlockBytes = 64u )
    {
        common::cooked_shader_binding_source_t bindings[4]{};
        bindings[0].name = Text( "View" );
        bindings[1].name = Text( "Model" );
        bindings[2].name = Text( "Material" );
        for ( common::u32 i = 0u; i < 3u; ++i ) {
            auto &binding = bindings[i];
            binding.kind = common::render_shader_binding_kind_t::UNIFORM_BUFFER;
            binding.resourceType = common::render_shader_resource_type_t::UNIFORM_BUFFER;
            binding.stageMask = common::RENDER_SHADER_STAGE_MASK_ALL_GRAPHICS;
            binding.flags = common::COOKED_SHADER_BINDING_FLAG_REQUIRED |
                ( i == 2u ? common::COOKED_SHADER_BINDING_FLAG_MATERIAL
                          : common::COOKED_SHADER_BINDING_FLAG_INSTANCE );
            binding.cbByteSize = i == 2u ? materialBlockBytes :
                i == 1u ? modelBlockBytes : 64u;
            binding.nLogicalBinding = BindingId( binding.name );
        }
        bindings[3].name = shaderValueName;
        bindings[3].kind = common::render_shader_binding_kind_t::VALUE;
        bindings[3].valueType = shaderValueType;
        bindings[3].stageMask = common::RENDER_SHADER_STAGE_MASK_FRAGMENT;
        bindings[3].flags = common::COOKED_SHADER_BINDING_FLAG_REQUIRED |
            common::COOKED_SHADER_BINDING_FLAG_MATERIAL;
        bindings[3].cbByteSize = common::CookedShader_ValueTypeStorageSize(
            shaderValueType );
        bindings[3].nLogicalBinding = BindingId( shaderValueName );
        const common::cooked_shader_interface_source_t shaderInterface{
            { bindings, 4u }
        };
        REQUIRE( common::CookedShader_ComputeInterfaceHash(
            shaderInterface, &interfaceHash ) );
        constexpr char vertex[] =
            "#version 410 core\n"
            "layout(std140) uniform View { mat4 view; };\n"
            "layout(std140) uniform Model { mat4 model; };\n"
            "void main(){gl_Position=view*model*vec4(0.0,0.0,0.0,1.0);}\n";
        constexpr char litVertex[] =
            "#version 410 core\n"
            "layout(std140) uniform View { mat4 view; };\n"
            "layout(std140) uniform Model { mat4 model; mat3 normalMatrix; };\n"
            "void main(){gl_Position=view*model*vec4(normalMatrix[0],1.0);}\n";
        constexpr char cutoutVertex[] =
            "#version 410 core\n"
            "layout(std140) uniform View { mat4 view; };\n"
            "layout(std140) uniform Model { mat4 model; vec4 cutout; };\n"
            "out float alphaCutoff;\n"
            "void main(){alphaCutoff=cutout.x;gl_Position=view*model*vec4(0,0,0,1);}\n";
        constexpr char litCutoutVertex[] =
            "#version 410 core\n"
            "layout(std140) uniform View { mat4 view; };\n"
            "layout(std140) uniform Model { mat4 model; mat3 normalMatrix; vec4 cutout; };\n"
            "out float alphaCutoff;\n"
            "void main(){alphaCutoff=cutout.x;gl_Position=view*model*vec4(normalMatrix[0],1.0);}\n";
        constexpr char fragment[] =
            "#version 410 core\n"
            "layout(std140) uniform Material { vec4 tint; };\n"
            "out vec4 c;void main(){c=tint;}\n";
        constexpr char cutoutFragment[] =
            "#version 410 core\n"
            "layout(std140) uniform Material { vec4 tint; };\n"
            "in float alphaCutoff;out vec4 c;\n"
            "void main(){if(alphaCutoff>1.0)discard;c=tint;}\n";
        common::cooked_shader_stage_source_t stages[2]{};
        stages[0].stage = common::render_shader_stage_t::VERTEX;
        if ( modelBlockBytes == 128u ) {
            stages[0].code = { reinterpret_cast<const common::byte *>( litCutoutVertex ),
                sizeof( litCutoutVertex ) };
        } else if ( modelBlockBytes == 112u ) {
            stages[0].code = { reinterpret_cast<const common::byte *>( litVertex ),
                sizeof( litVertex ) };
        } else if ( modelBlockBytes == 80u ) {
            stages[0].code = { reinterpret_cast<const common::byte *>( cutoutVertex ),
                sizeof( cutoutVertex ) };
        } else {
            stages[0].code = { reinterpret_cast<const common::byte *>( vertex ),
                sizeof( vertex ) };
        }
        stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        stages[1].code = modelBlockBytes == 80u || modelBlockBytes == 128u
            ? common::binary_block_t{
                reinterpret_cast<const common::byte *>( cutoutFragment ),
                sizeof( cutoutFragment ) }
            : common::binary_block_t{
                reinterpret_cast<const common::byte *>( fragment ),
                sizeof( fragment ) };
        shader.resize( common::CookedShader_RequiredSizeV3(
            {}, { stages, 2u }, shaderInterface ) );
        REQUIRE_FALSE( shader.empty() );
        REQUIRE( common::CookedShader_Succeeded( common::CookedShader_WriteV3(
            {}, { stages, 2u }, shaderInterface,
            common::ContentHash_String( Text( "shaders/bridge.cyshader" ) ),
            { shader.data(), shader.size() } ) ) );

        common::cooked_material_parameter_source_v2_t tint{};
        tint.name = Text( "tint" );
        tint.nLogicalBinding = BindingId( tint.name );
        tint.type = common::render_shader_value_type_t::F32X4;
        tint.cbByteSize = 16u;
        tint.floatingValues[0] = 0.25;
        tint.floatingValues[1] = 0.5;
        tint.floatingValues[2] = 0.75;
        tint.floatingValues[3] = 1.0;
        common::cooked_material_source_v2_t description{};
        description.shader = Text( "shaders/bridge.cyshader" );
        description.shaderInterfaceHash = interfaceHash;
        description.parameters = { &tint, 1u };
        material.resize( common::CookedMaterial_RequiredSizeV2( description ) );
        REQUIRE_FALSE( material.empty() );
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

    static void MakeFloatTexture( std::vector<common::byte> &file,
        bool authoredMips,
        common::render_texture_usage_t usage = common::render_texture_usage_t::COLOR,
        common::cooked_texture_residency_t residency =
            common::cooked_texture_residency_t::FULLY_RESIDENT )
    {
        // The second level starts at byte 64. These are finite, exactly
        // representable values, including a channel above display white.
        const float pixels[20]{ 4.0F, 0.5F, 0.25F, 1.0F,
            2.0F, 0.25F, 0.5F, 1.0F, 1.0F, 0.5F, 0.25F, 1.0F,
            0.5F, 0.25F, 0.125F, 1.0F, 3.0F, 0.5F, 0.25F, 1.0F };
        const auto *bytes = reinterpret_cast<const common::byte *>( pixels );
        common::cooked_texture_desc_t description{};
        description.pixelFormat = common::render_texture_pixel_format_t::RGBA32_FLOAT;
        description.storageFormat = common::render_format_t::RGBA32_FLOAT;
        description.colorSpace = common::render_texture_color_space_t::LINEAR;
        description.usage = usage;
        description.residency = residency;
        if ( usage == common::render_texture_usage_t::DATA ) {
            description.alphaMode = common::cooked_texture_alpha_mode_t::DATA;
        }
        description.nWidth = authoredMips ? 2u : 1u;
        description.nHeight = authoredMips ? 2u : 1u;
        description.nMipLevels = authoredMips ? 2u : 1u;
        if ( residency == common::cooked_texture_residency_t::MIP_STREAMED ) {
            description.nResidentMipLevels = 1u;
        }
        const common::cooked_texture_mip_source_t mips[2]{
            { description.nWidth, description.nHeight, 1u,
                description.nWidth * 16u,
                { bytes, authoredMips ? 64u : 16u } },
            { 1u, 1u, 1u, 16u, { bytes + 64u, 16u } }
        };
        const common::span_t<const common::cooked_texture_mip_source_t> source{
            mips, description.nMipLevels
        };
        file.resize( common::CookedTexture_RequiredSize( description, source ) );
        REQUIRE( !file.empty() );
        REQUIRE( common::CookedTexture_Succeeded( common::CookedTexture_Write(
            description, source,
            common::ContentHash_String( Text( "textures/albedo.cytex" ) ),
            { file.data(), file.size() } ) ) );
    }

    static void MakeIncompleteMipTexture( std::vector<common::byte> &file,
        bool floating )
    {
        // CYTX permits a partial authored chain. The material bridge cannot
        // adopt it because its current sampler path requires the full chain.
        const common::byte pixels[320]{};
        const common::u32 bytesPerPixel = floating ? 16u : 4u;
        common::cooked_texture_desc_t description{};
        description.nWidth = description.nHeight = 4u;
        description.nMipLevels = 2u; // The required 1x1 level is absent.
        if ( floating ) {
            description.pixelFormat = common::render_texture_pixel_format_t::RGBA32_FLOAT;
            description.storageFormat = common::render_format_t::RGBA32_FLOAT;
            description.colorSpace = common::render_texture_color_space_t::LINEAR;
        }
        const common::cooked_texture_mip_source_t mips[2]{
            { 4u, 4u, 1u, 4u * bytesPerPixel,
                { pixels, 16u * bytesPerPixel } },
            { 2u, 2u, 1u, 2u * bytesPerPixel,
                { pixels + 16u * bytesPerPixel, 4u * bytesPerPixel } }
        };
        const common::span_t<const common::cooked_texture_mip_source_t> source{
            mips, 2u
        };
        file.resize( common::CookedTexture_RequiredSize( description, source ) );
        REQUIRE( !file.empty() );
        REQUIRE( common::CookedTexture_Succeeded( common::CookedTexture_Write(
            description, source,
            common::ContentHash_String( Text( "textures/albedo.cytex" ) ),
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
    common::u32 depthTextureCreates{ 0u }, samplerCreates{ 0u }, samplerDestroys{ 0u };
    common::u32 bufferCreates{ 0u }, bufferDestroys{ 0u };
    bool failBufferCreate{ false };
    bool tokenOnBufferFailure{ false };
    bool failNextBufferDestroy{ false };
    common::f32 uploadedTint[4]{};
    common::u64 uploadedBufferBytes{ 0u };
    render::render_buffer_usage_flags_t uploadedBufferUsage{ render::R_BUFFER_USAGE_NONE };
    common::u32 failTextureAt{ 0u };
    bool tokenOnTextureFailure{ false };
    bool failNextTextureDestroy{ false };
    render::render_error_t pipelineResult{ render::render_error_t::OK };
    common::u32 failPipelineAt{ 0u };
    bool tokenOnPipelineFailure{ false };
    bool failNextPipelineDestroy{ false };
    common::u32 pipelineDestroyAttempts{ 0u };
    bool pipelineFrontCounterClockwise[2]{};
    bool pipelineCullBackFaces[2]{};
    bool pipelineDepthWrite[2]{};
    render::render_blend_mode_t pipelineBlendModes[2]{};
    common::u32 lastPipelineTextureCount{ 0u };
    render::render_sampled_texture_kind_t lastPipelineSecondTextureKind{
        render::render_sampled_texture_kind_t::SCALAR_2D };
    render::render_blend_mode_t lastPipelineBlendMode{
        render::render_blend_mode_t::DISABLED };
    bool lastPipelineDepthWrite{ true };
    bool pipelineTextureNamesMatch{ false };
    bool lastPipelineSecondIsShadowMap{ false };
    common::byte uploadedPixels[2][4]{};
    common::byte uploadedLevelPixels[2][2][64]{};
    common::u32 uploadedLevelSizes[2][2]{};
    common::u32 uploadedLevelCount[2]{};
    render::render_texture_format_t uploadedFormats[2]{};
    bool uploadedSrgb[2]{};

    render::render_error_t CaptureTexture(
        const render::render_texture_desc_t &description,
        const render::render_texture_mip_data_t *levels,
        common::u32 levelCount, render::backend_texture_t &out ) noexcept
    {
        if ( description.renderTarget &&
             description.format == render::render_texture_format_t::D24 &&
             levels != nullptr && levelCount == 1u &&
             levels[0].data.bytes == nullptr && levels[0].data.byteSize == 0u ) {
            ++depthTextureCreates;
            out.value = nextToken++;
            return render::render_error_t::OK;
        }
        const common::u32 index = textureCreates++;
        out = {};
        if ( index >= 2u || levels == nullptr || levelCount == 0u ||
             levelCount > 2u ) return render::render_error_t::ERR_INVALID_ARGUMENT;
        const common::u32 bytesPerPixel =
            description.format == render::render_texture_format_t::RGBA32F ? 16u : 4u;
        if ( description.format != render::render_texture_format_t::RGBA32F &&
             description.format != render::render_texture_format_t::RGBA8 ) {
            return render::render_error_t::ERR_INVALID_ARGUMENT;
        }
        common::u32 width = description.width;
        common::u32 height = description.height;
        for ( common::u32 level = 0u; level < levelCount; ++level ) {
            const common::u32 expectedBytes = width * height * bytesPerPixel;
            if ( levels[level].width != width || levels[level].height != height ||
                 levels[level].data.bytes == nullptr ||
                 levels[level].data.byteSize != expectedBytes || expectedBytes > 64u ) {
                return render::render_error_t::ERR_INVALID_ARGUMENT;
            }
            uploadedLevelSizes[index][level] = expectedBytes;
            std::memcpy( uploadedLevelPixels[index][level],
                levels[level].data.bytes, expectedBytes );
            width = width > 1u ? width / 2u : 1u;
            height = height > 1u ? height / 2u : 1u;
        }
        uploadedLevelCount[index] = levelCount;
        uploadedFormats[index] = description.format;
        uploadedSrgb[index] = description.sRGB;
        std::memcpy( uploadedPixels[index], uploadedLevelPixels[index][0], 4u );
        if ( failTextureAt == textureCreates ) {
            if ( tokenOnTextureFailure ) out.value = nextToken++;
            return render::render_error_t::ERR_OUT_OF_MEMORY;
        }
        out.value = nextToken++;
        return render::render_error_t::OK;
    }

    void Init( common::u32 textureCount, bool mismatch = false,
        bool withFeature = false, bool withSampler = false,
        common::render_material_alpha_mode_t alphaMode =
            common::render_material_alpha_mode_t::OPAQUE,
        common::f64 maskCutoff = 0.375,
        common::flags32_t materialFlags = common::COOKED_MATERIAL_FLAG_NONE )
    {
        cooked.Build( textureCount, mismatch, withFeature, withSampler,
            alphaMode, maskCutoff, materialFlags );
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
            const common::u32 index = fixture.pipelineCreates++;
            if ( index < 2u ) {
                fixture.pipelineFrontCounterClockwise[index] = description.frontCounterClockwise;
                fixture.pipelineCullBackFaces[index] = description.cullBackFaces;
                fixture.pipelineDepthWrite[index] = description.depthWrite;
                fixture.pipelineBlendModes[index] = description.blendMode;
            }
            fixture.lastPipelineTextureCount = description.sampledTextureCount;
            if ( description.sampledTextureCount == 2u ) {
                fixture.lastPipelineSecondTextureKind =
                    description.sampledTextures[1].kind;
                fixture.lastPipelineSecondIsShadowMap =
                    description.sampledTextures[1].name != nullptr &&
                    std::strcmp( description.sampledTextures[1].name, "ShadowMap" ) == 0;
            }
            fixture.lastPipelineBlendMode = description.blendMode;
            fixture.lastPipelineDepthWrite = description.depthWrite;
            fixture.pipelineTextureNamesMatch = description.sampledTextureCount == 0u ||
                ( description.sampledTextures[0].name != nullptr &&
                  std::strcmp( description.sampledTextures[0].name, "AlbedoMap" ) == 0 &&
                  ( description.sampledTextureCount == 1u ||
                    ( description.sampledTextures[1].name != nullptr &&
                      std::strcmp( description.sampledTextures[1].name, "DetailMap" ) == 0 ) ) );
            out = {};
            const render::render_error_t result =
                fixture.failPipelineAt == fixture.pipelineCreates
                    ? render::render_error_t::ERR_OUT_OF_MEMORY
                    : fixture.pipelineResult;
            if ( result != render::render_error_t::OK ) {
                if ( fixture.tokenOnPipelineFailure ) out.value = fixture.nextToken++;
                return result;
            }
            out.value = fixture.nextToken++;
            return render::render_error_t::OK;
        };
        backend.DestroyGraphicsPipeline = []( render::backend_pipeline_t, void *state ) noexcept {
            auto &fixture = *static_cast<bridge_fixture_t *>( state );
            ++fixture.pipelineDestroyAttempts;
            if ( fixture.failNextPipelineDestroy ) {
                fixture.failNextPipelineDestroy = false;
                return render::render_error_t::ERR_DEVICE_LOST;
            }
            ++fixture.pipelineDestroys;
            return render::render_error_t::OK;
        };
        backend.CreateTexture2D = []( const render::render_texture_desc_t &description,
            const render::render_texture_data_t &data,
            render::backend_texture_t &out, void *state ) noexcept {
            auto &fixture = *static_cast<bridge_fixture_t *>( state );
            const render::render_texture_mip_data_t level{
                description.width, description.height, data
            };
            return fixture.CaptureTexture( description, &level, 1u, out );
        };
        backend.CreateTexture2DLevels = [](
            const render::render_texture_desc_t &description,
            const render::render_texture_mip_data_t *levels,
            common::u32 levelCount, render::backend_texture_t &out,
            void *state ) noexcept {
            return static_cast<bridge_fixture_t *>( state )->CaptureTexture(
                description, levels, levelCount, out );
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
        backend.CreateSampler = []( const render::render_sampler_desc_t &,
            render::backend_sampler_t &out, void *state ) noexcept {
            auto &fixture = *static_cast<bridge_fixture_t *>( state );
            ++fixture.samplerCreates;
            out.value = fixture.nextToken++;
            return render::render_error_t::OK;
        };
        backend.DestroySampler = []( render::backend_sampler_t, void *state ) noexcept {
            ++static_cast<bridge_fixture_t *>( state )->samplerDestroys;
            return render::render_error_t::OK;
        };
        backend.CreateBuffer = []( const render::render_buffer_desc_t &description,
            const render::render_buffer_data_t *data,
            render::backend_buffer_t &out, void *state ) noexcept {
            auto &fixture = *static_cast<bridge_fixture_t *>( state );
            ++fixture.bufferCreates;
            out = {};
            if ( data == nullptr || data->bytes == nullptr ||
                 description.byteSize != sizeof( fixture.uploadedTint ) ||
                 data->byteSize != sizeof( fixture.uploadedTint ) ||
                 description.usage != render::R_BUFFER_USAGE_UNIFORM ) {
                return render::render_error_t::ERR_INVALID_ARGUMENT;
            }
            fixture.uploadedBufferBytes = data->byteSize;
            fixture.uploadedBufferUsage = description.usage;
            std::memcpy( fixture.uploadedTint, data->bytes,
                static_cast<common::usize>( data->byteSize ) );
            if ( fixture.failBufferCreate ) {
                if ( fixture.tokenOnBufferFailure ) out.value = fixture.nextToken++;
                return render::render_error_t::ERR_OUT_OF_MEMORY;
            }
            out.value = fixture.nextToken++;
            return render::render_error_t::OK;
        };
        backend.DestroyBuffer = []( render::backend_buffer_t, void *state ) noexcept {
            auto &fixture = *static_cast<bridge_fixture_t *>( state );
            if ( fixture.failNextBufferDestroy ) {
                fixture.failNextBufferDestroy = false;
                return render::render_error_t::ERR_DEVICE_LOST;
            }
            ++fixture.bufferDestroys;
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
        REQUIRE( render::R_BufferSystemInit() == render::render_error_t::OK );
        REQUIRE( render::R_ShaderSystemInit() == render::render_error_t::OK );
        REQUIRE( render::R_TextureSystemInit() == render::render_error_t::OK );
        REQUIRE( render::R_SamplerSystemInit() == render::render_error_t::OK );
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

    void UseTint( common::string_view_t shaderValueName = Text( "tint" ),
        common::render_shader_value_type_t shaderValueType =
            common::render_shader_value_type_t::F32X4,
        common::u32 materialBlockBytes = 16u,
        common::u32 modelBlockBytes = 64u )
    {
        cooked.BuildTint( shaderValueName, shaderValueType,
            materialBlockBytes, modelBlockBytes );
        storage.files[0].contents = { cooked.material.data(), cooked.material.size() };
        storage.files[1].contents = { cooked.shader.data(), cooked.shader.size() };
    }

    void UsePassShadow( bool passMarkedMaterial = false,
        bool albedoMarkedMaterial = true )
    {
        cooked.BuildPassShadow( passMarkedMaterial, albedoMarkedMaterial );
        storage.files[0].contents = { cooked.material.data(), cooked.material.size() };
        storage.files[1].contents = { cooked.shader.data(), cooked.shader.size() };
    }

    render::render_material_bundle_desc_t TintDescription()
    {
        auto description = Description();
        description.uniformBlockCount = 3u;
        description.uniformBlocks[0] = { "View", 64u };
        description.uniformBlocks[1] = { "Model", 64u };
        description.uniformBlocks[2] = { "Material", 16u };
        return description;
    }

    void UseFloatAlbedo( bool authoredMips,
        common::render_texture_usage_t usage = common::render_texture_usage_t::COLOR,
        common::cooked_texture_residency_t residency =
            common::cooked_texture_residency_t::FULLY_RESIDENT )
    {
        cooked.MakeFloatTexture( cooked.albedo, authoredMips, usage, residency );
        storage.files[2].contents = { cooked.albedo.data(), cooked.albedo.size() };
    }

    ~bridge_fixture_t()
    {
        (void)render::R_DestroyCookedMaterialBundle( &bundle );
        (void)render::R_PipelineSystemShutdown();
        (void)render::R_SamplerSystemShutdown();
        (void)render::R_TextureSystemShutdown();
        (void)render::R_ShaderSystemShutdown();
        (void)render::R_BufferSystemShutdown();
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
        CHECK( fixture.pipelineCreates == 2u );
        CHECK( fixture.textureCreates == textureCount );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
        CHECK( fixture.lastPipelineTextureCount == textureCount );
        CHECK( fixture.lastPipelineBlendMode == render::render_blend_mode_t::DISABLED );
        CHECK( fixture.lastPipelineDepthWrite );
        CHECK( fixture.pipelineTextureNamesMatch );
        CHECK( fixture.bundle.textureCount == textureCount );
        CHECK( fixture.bundle.alphaMode == common::render_material_alpha_mode_t::OPAQUE );
        CHECK( fixture.bundle.alphaCutoff == 0.5F );
        CHECK( render::R_IsShaderValid( fixture.bundle.shader ) );
        CHECK( render::R_IsGraphicsPipelineValid( fixture.bundle.pipeline ) );
        CHECK( render::R_IsGraphicsPipelineValid( fixture.bundle.mirroredPipeline ) );
        CHECK( fixture.pipelineFrontCounterClockwise[0] );
        CHECK_FALSE( fixture.pipelineFrontCounterClockwise[1] );
        CHECK( fixture.pipelineCullBackFaces[0] );
        CHECK( fixture.pipelineCullBackFaces[1] );
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
        REQUIRE( render::R_ApplyCookedMaterialBundleToDraw(
            fixture.bundle, true, &draw ) == render::render_error_t::OK );
        CHECK( draw.pipeline.value == fixture.bundle.mirroredPipeline.value );
        for ( common::u32 index = 0u; index < textureCount; ++index ) {
            CHECK( draw.sampledTextures[index].value == fixture.bundle.textures[index].value );
        }
        REQUIRE( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
            render::render_error_t::OK );
        CHECK( fixture.pipelineDestroys == 2u );
        CHECK( fixture.shaderDestroys == 1u );
        CHECK( fixture.textureDestroys == textureCount );
        CHECK( fixture.bundle.mirroredPipeline.value == 0u );
    }
}

TEST_CASE( "one cooked albedo and a borrowed pass shadow map occupy separate draw slots",
    "[CypherRender][MaterialBridge][Shadow]" )
{
    bridge_fixture_t fixture{};
    fixture.Init( 1u );
    fixture.UsePassShadow();
    auto description = fixture.Description();
    description.passShadowMapBinding = "ShadowMap";
    REQUIRE( render::R_CreateCookedMaterialBundle(
        description, &fixture.bundle ).status ==
        render::render_material_bridge_status_t::OK );
    CHECK( fixture.bundle.textureCount == 1u );
    CHECK( fixture.bundle.passShadowSlot == 1u );
    CHECK( fixture.textureCreates == 1u );
    CHECK( fixture.lastPipelineTextureCount == 2u );
    CHECK( fixture.lastPipelineSecondIsShadowMap );
    CHECK( fixture.lastPipelineSecondTextureKind ==
        render::render_sampled_texture_kind_t::DEPTH_COMPARE_2D );
    CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );

    render::render_texture_desc_t depthDesc{};
    depthDesc.width = depthDesc.height = 2u;
    depthDesc.sRGB = depthDesc.generateMips = depthDesc.repeat = false;
    depthDesc.renderTarget = true;
    depthDesc.format = render::render_texture_format_t::D24;
    render::render_texture_handle_t depth{};
    REQUIRE( render::R_CreateTexture2D( depthDesc, {}, &depth ) ==
        render::render_error_t::OK );
    render::render_sampler_desc_t compareDesc{};
    compareDesc.addressU = compareDesc.addressV =
        render::render_sampler_address_t::CLAMP_TO_EDGE;
    compareDesc.compare = render::render_sampler_compare_t::LESS_EQUAL;
    render::render_sampler_handle_t compare{};
    REQUIRE( render::R_CreateSampler( compareDesc, &compare ) ==
        render::render_error_t::OK );

    render::render_draw_indexed_desc_t draw{};
    draw.sampledTextures[1] = depth;
    draw.samplers[1] = compare;
    REQUIRE( render::R_ApplyCookedMaterialBundleToDraw(
        fixture.bundle, &draw ) == render::render_error_t::OK );
    CHECK( draw.pipeline.value == fixture.bundle.pipeline.value );
    CHECK( draw.sampledTextures[0].value == fixture.bundle.textures[0].value );
    CHECK( draw.sampledTextures[1].value == depth.value );
    CHECK( draw.samplers[1].value == compare.value );
    REQUIRE( render::R_ApplyCookedMaterialBundleToDraw(
        fixture.bundle, true, &draw ) == render::render_error_t::OK );
    CHECK( draw.pipeline.value == fixture.bundle.mirroredPipeline.value );
    CHECK( draw.sampledTextures[1].value == depth.value );
    CHECK( draw.samplers[1].value == compare.value );

    REQUIRE( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
        render::render_error_t::OK );
    CHECK( fixture.textureDestroys == 1u );
    CHECK( render::R_IsTextureValid( depth ) );
    CHECK( render::R_IsSamplerValid( compare ) );
    REQUIRE( render::R_DestroySampler( compare ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( depth ) == render::render_error_t::OK );
    CHECK( fixture.textureDestroys == 2u );
    CHECK( fixture.samplerDestroys == 1u );
}

TEST_CASE( "pass shadow binding preflight leaves a draw unchanged on bad inputs",
    "[CypherRender][MaterialBridge][Shadow]" )
{
    bridge_fixture_t fixture{};
    fixture.Init( 1u );
    fixture.UsePassShadow();
    auto description = fixture.Description();
    description.passShadowMapBinding = "ShadowMap";
    REQUIRE( render::R_CreateCookedMaterialBundle(
        description, &fixture.bundle ).status ==
        render::render_material_bridge_status_t::OK );

    render::render_texture_desc_t depthDesc{};
    depthDesc.width = depthDesc.height = 2u;
    depthDesc.sRGB = depthDesc.generateMips = depthDesc.repeat = false;
    depthDesc.renderTarget = true;
    depthDesc.format = render::render_texture_format_t::D24;
    render::render_texture_handle_t depth{};
    REQUIRE( render::R_CreateTexture2D( depthDesc, {}, &depth ) ==
        render::render_error_t::OK );
    render::render_sampler_desc_t compareDesc{};
    compareDesc.compare = render::render_sampler_compare_t::LESS_EQUAL;
    render::render_sampler_handle_t compare{};
    REQUIRE( render::R_CreateSampler( compareDesc, &compare ) ==
        render::render_error_t::OK );
    render::render_sampler_desc_t scalarDesc{};
    render::render_sampler_handle_t scalar{};
    REQUIRE( render::R_CreateSampler( scalarDesc, &scalar ) ==
        render::render_error_t::OK );
    render::render_sampler_desc_t mipDesc = compareDesc;
    mipDesc.mipFilter = render::render_sampler_mip_filter_t::LINEAR;
    render::render_sampler_handle_t mipCompare{};
    REQUIRE( render::R_CreateSampler( mipDesc, &mipCompare ) ==
        render::render_error_t::OK );

    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = fixture.bundle.mirroredPipeline;
    auto checkRejected = [&]( render::render_error_t expected ) {
        const auto pipelineBefore = draw.pipeline;
        const auto albedoBefore = draw.sampledTextures[0];
        const auto depthBefore = draw.sampledTextures[1];
        const auto samplerBefore = draw.samplers[1];
        CHECK( render::R_ApplyCookedMaterialBundleToDraw(
            fixture.bundle, &draw ) == expected );
        CHECK( draw.pipeline.value == pipelineBefore.value );
        CHECK( draw.sampledTextures[0].value == albedoBefore.value );
        CHECK( draw.sampledTextures[1].value == depthBefore.value );
        CHECK( draw.samplers[1].value == samplerBefore.value );
    };
    draw.samplers[1] = compare;
    checkRejected( render::render_error_t::ERR_INVALID_HANDLE );
    draw.sampledTextures[1] = fixture.bundle.textures[0];
    checkRejected( render::render_error_t::ERR_BINDING_MISMATCH );
    draw.sampledTextures[1] = depth;
    draw.samplers[1] = {};
    checkRejected( render::render_error_t::ERR_INVALID_HANDLE );
    draw.samplers[1] = scalar;
    checkRejected( render::render_error_t::ERR_BINDING_MISMATCH );
    draw.samplers[1] = mipCompare;
    checkRejected( render::render_error_t::ERR_BINDING_MISMATCH );

    REQUIRE( render::R_DestroySampler( scalar ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroySampler( mipCompare ) == render::render_error_t::OK );
    draw.samplers[1] = scalar;
    checkRejected( render::render_error_t::ERR_STALE_HANDLE );
    draw.samplers[1] = compare;
    REQUIRE( render::R_DestroyTexture( depth ) == render::render_error_t::OK );
    checkRejected( render::render_error_t::ERR_STALE_HANDLE );

    REQUIRE( render::R_DestroySampler( compare ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
        render::render_error_t::OK );
}

TEST_CASE( "pass shadow bundle rejects mismatched ownership before GPU creation",
    "[CypherRender][MaterialBridge][Shadow]" )
{
    SECTION( "unknown or aliased pass binding" ) {
        for ( const char *name : { "MissingShadow", "AlbedoMap" } ) {
            bridge_fixture_t fixture{};
            fixture.Init( 1u );
            fixture.UsePassShadow();
            auto description = fixture.Description();
            description.passShadowMapBinding = name;
            CHECK( render::R_CreateCookedMaterialBundle(
                description, &fixture.bundle ).status ==
                render::render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH );
            CHECK( fixture.shaderCreates == 0u );
            CHECK( fixture.textureCreates == 0u );
            CHECK( fixture.pipelineCreates == 0u );
            CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
        }
    }
    SECTION( "pass resource is marked material-owned" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u );
        fixture.UsePassShadow( true );
        auto description = fixture.Description();
        description.passShadowMapBinding = "ShadowMap";
        CHECK( render::R_CreateCookedMaterialBundle(
            description, &fixture.bundle ).status ==
            render::render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.textureCreates == 0u );
    }
    SECTION( "albedo resource is not marked material-owned" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u );
        fixture.UsePassShadow( false, false );
        auto description = fixture.Description();
        description.passShadowMapBinding = "ShadowMap";
        CHECK( render::R_CreateCookedMaterialBundle(
            description, &fixture.bundle ).status ==
            render::render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.textureCreates == 0u );
    }
    SECTION( "zero or two material textures exceed this pass contract" ) {
        for ( common::u32 textureCount : { 0u, 2u } ) {
            bridge_fixture_t fixture{};
            fixture.Init( textureCount );
            auto description = fixture.Description();
            description.passShadowMapBinding = "ShadowMap";
            CHECK( render::R_CreateCookedMaterialBundle(
                description, &fixture.bundle ).status ==
                render::render_material_bridge_status_t::MATERIAL_UNSUPPORTED );
            CHECK( fixture.shaderCreates == 0u );
            CHECK( fixture.textureCreates == 0u );
            CHECK( fixture.pipelineCreates == 0u );
            CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
        }
    }
}

TEST_CASE( "pass shadow bridge rollback owns only the cooked albedo",
    "[CypherRender][MaterialBridge][Shadow]" )
{
    bridge_fixture_t fixture{};
    fixture.Init( 1u );
    fixture.UsePassShadow();
    fixture.failPipelineAt = 2u;
    auto description = fixture.Description();
    description.passShadowMapBinding = "ShadowMap";
    const auto result = render::R_CreateCookedMaterialBundle(
        description, &fixture.bundle );
    CHECK( result.status == render::render_material_bridge_status_t::RENDER_ERROR );
    CHECK( result.renderError == render::render_error_t::ERR_OUT_OF_MEMORY );
    CHECK( fixture.textureCreates == 1u );
    CHECK( fixture.textureDestroys == 1u );
    CHECK( fixture.depthTextureCreates == 0u );
    CHECK( fixture.samplerCreates == 0u );
    CHECK( fixture.bundle.shader.value == 0u );
    CHECK( fixture.bundle.pipeline.value == 0u );
    CHECK( fixture.bundle.textures[0].value == 0u );
    CHECK( fixture.bundle.passShadowSlot == render::R_MAX_SAMPLED_TEXTURE_BINDINGS );
    CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
}

TEST_CASE( "cooked material bridge rejects replacing a live bundle without losing its handles",
    "[CypherRender][MaterialBridge]" )
{
    for ( const bool withTint : { false, true } ) {
        bridge_fixture_t fixture{};
        fixture.Init( withTint ? 0u : 1u );
        if ( withTint ) fixture.UseTint();
        const auto description = withTint
            ? fixture.TintDescription() : fixture.Description();
        REQUIRE( render::R_CreateCookedMaterialBundle(
            description, &fixture.bundle ).status ==
            render::render_material_bridge_status_t::OK );

        const auto original = fixture.bundle;
        const auto reads = fixture.storage.reads;
        const auto second = render::R_CreateCookedMaterialBundle(
            description, &fixture.bundle );
        CHECK( second.status == render::render_material_bridge_status_t::INVALID_ARGUMENT );
        CHECK( fixture.storage.reads == reads );
        CHECK( fixture.shaderCreates == 1u );
        CHECK( fixture.pipelineCreates == 2u );
        CHECK( fixture.textureCreates == ( withTint ? 0u : 1u ) );
        CHECK( fixture.bufferCreates == ( withTint ? 1u : 0u ) );
        CHECK( fixture.bundle.shader.value == original.shader.value );
        CHECK( fixture.bundle.pipeline.value == original.pipeline.value );
        CHECK( fixture.bundle.mirroredPipeline.value == original.mirroredPipeline.value );
        CHECK( fixture.bundle.textures[0].value == original.textures[0].value );
        CHECK( fixture.bundle.materialUniformBuffer.value ==
            original.materialUniformBuffer.value );
        CHECK( render::R_IsShaderValid( original.shader ) );
        CHECK( render::R_IsGraphicsPipelineValid( original.pipeline ) );
        CHECK( render::R_IsGraphicsPipelineValid( original.mirroredPipeline ) );
        if ( withTint ) {
            CHECK( render::R_IsBufferValid( original.materialUniformBuffer ) );
        } else {
            CHECK( render::R_IsTextureValid( original.textures[0] ) );
        }

        render::render_draw_indexed_desc_t draw{};
        CHECK( render::R_ApplyCookedMaterialBundleToDraw(
            fixture.bundle, &draw ) == render::render_error_t::OK );
        REQUIRE( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
            render::render_error_t::OK );
        CHECK( fixture.pipelineDestroys == 2u );
        CHECK( fixture.shaderDestroys == 1u );
        CHECK( fixture.textureDestroys == ( withTint ? 0u : 1u ) );
        CHECK( fixture.bufferDestroys == ( withTint ? 1u : 0u ) );
    }
}

TEST_CASE( "cooked material mirror keeps alpha state and two-sided front-facing policy",
    "[CypherRender][MaterialBridge][Mirror]" )
{
    for ( const auto mode : {
             common::render_material_alpha_mode_t::OPAQUE,
             common::render_material_alpha_mode_t::MASK,
             common::render_material_alpha_mode_t::BLEND,
             common::render_material_alpha_mode_t::ADDITIVE } ) {
        CAPTURE( static_cast<common::u32>( mode ) );
        bridge_fixture_t fixture{};
        fixture.Init( 1u, false, false, false, mode, 0.375,
            common::COOKED_MATERIAL_FLAG_TWO_SIDED );
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        REQUIRE( result.status == render::render_material_bridge_status_t::OK );
        REQUIRE( fixture.pipelineCreates == 2u );
        CHECK( fixture.pipelineFrontCounterClockwise[0] );
        CHECK_FALSE( fixture.pipelineFrontCounterClockwise[1] );
        CHECK_FALSE( fixture.pipelineCullBackFaces[0] );
        CHECK_FALSE( fixture.pipelineCullBackFaces[1] );
        CHECK( fixture.pipelineDepthWrite[0] == fixture.pipelineDepthWrite[1] );
        CHECK( fixture.pipelineBlendModes[0] == fixture.pipelineBlendModes[1] );

        render::render_pipeline_info_t ordinary{}, mirrored{};
        REQUIRE( render::R_GetGraphicsPipelineInfo( fixture.bundle.pipeline,
            &ordinary ) == render::render_error_t::OK );
        REQUIRE( render::R_GetGraphicsPipelineInfo( fixture.bundle.mirroredPipeline,
            &mirrored ) == render::render_error_t::OK );
        CHECK( ordinary.shader.value == mirrored.shader.value );
        CHECK( ordinary.frontCounterClockwise );
        CHECK_FALSE( mirrored.frontCounterClockwise );
        CHECK_FALSE( ordinary.cullBackFaces );
        CHECK_FALSE( mirrored.cullBackFaces );
        CHECK( ordinary.depthTest == mirrored.depthTest );
        CHECK( ordinary.depthWrite == mirrored.depthWrite );
        CHECK( ordinary.blendMode == mirrored.blendMode );
        CHECK( ordinary.sampledTextureCount == mirrored.sampledTextureCount );

        render::render_draw_indexed_desc_t draw{};
        REQUIRE( render::R_ApplyCookedMaterialBundleToDraw(
            fixture.bundle, true, &draw ) == render::render_error_t::OK );
        CHECK( draw.pipeline.value == fixture.bundle.mirroredPipeline.value );
        CHECK( draw.sampledTextures[0].value == fixture.bundle.textures[0].value );
        REQUIRE( render::R_ApplyCookedMaterialBundleToDraw(
            fixture.bundle, &draw ) == render::render_error_t::OK );
        CHECK( draw.pipeline.value == fixture.bundle.pipeline.value );
        const auto mirroredPipeline = fixture.bundle.mirroredPipeline;
        fixture.bundle.mirroredPipeline = {};
        CHECK( render::R_ApplyCookedMaterialBundleToDraw(
            fixture.bundle, true, &draw ) == render::render_error_t::ERR_UNSUPPORTED );
        CHECK( draw.pipeline.value == fixture.bundle.pipeline.value );
        fixture.bundle.mirroredPipeline = fixture.bundle.pipeline;
        CHECK( render::R_ApplyCookedMaterialBundleToDraw(
            fixture.bundle, true, &draw ) == render::render_error_t::ERR_BINDING_MISMATCH );
        CHECK( draw.pipeline.value == fixture.bundle.pipeline.value );
        fixture.bundle.mirroredPipeline = mirroredPipeline;
    }
}

TEST_CASE( "cooked tint creates a native float material uniform and owns its lifetime",
    "[CypherRender][MaterialBridge][Tint]" )
{
    bridge_fixture_t fixture{};
    fixture.Init( 0u );
    fixture.UseTint();
    const auto result = render::R_CreateCookedMaterialBundle(
        fixture.TintDescription(), &fixture.bundle );
    REQUIRE( result.status == render::render_material_bridge_status_t::OK );
    CHECK( fixture.shaderCreates == 1u );
    CHECK( fixture.bufferCreates == 1u );
    CHECK( fixture.pipelineCreates == 2u );
    CHECK( fixture.uploadedBufferBytes == 4u * sizeof( common::f32 ) );
    CHECK( fixture.uploadedBufferUsage == render::R_BUFFER_USAGE_UNIFORM );
    CHECK( fixture.uploadedTint[0] == 0.25F );
    CHECK( fixture.uploadedTint[1] == 0.5F );
    CHECK( fixture.uploadedTint[2] == 0.75F );
    CHECK( fixture.uploadedTint[3] == 1.0F );
    CHECK( fixture.bundle.materialUniformSlot == 2u );
    CHECK( render::R_IsBufferValid( fixture.bundle.materialUniformBuffer ) );
    CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );

    std::memset( fixture.cooked.material.data(), 0, fixture.cooked.material.size() );
    std::memset( fixture.cooked.shader.data(), 0, fixture.cooked.shader.size() );
    render::render_draw_indexed_desc_t draw{};
    draw.uniformBuffers[2].value = fixture.bundle.materialUniformBuffer.value + 1u;
    CHECK( render::R_ApplyCookedMaterialBundleToDraw( fixture.bundle, &draw ) ==
        render::render_error_t::ERR_BINDING_MISMATCH );
    CHECK( draw.pipeline.value == 0u );
    draw.uniformBuffers[2] = {};
    draw.uniformRanges[2] = { 16u, 16u };
    CHECK( render::R_ApplyCookedMaterialBundleToDraw( fixture.bundle, &draw ) ==
        render::render_error_t::ERR_BINDING_MISMATCH );
    draw.uniformRanges[2] = {};
    REQUIRE( render::R_ApplyCookedMaterialBundleToDraw(
        fixture.bundle, &draw ) == render::render_error_t::OK );
    CHECK( draw.pipeline.value == fixture.bundle.pipeline.value );
    CHECK( draw.uniformBuffers[2].value == fixture.bundle.materialUniformBuffer.value );
    CHECK( draw.uniformRanges[2].offset == 0u );
    CHECK( draw.uniformRanges[2].byteSize == 0u );
    REQUIRE( render::R_ApplyCookedMaterialBundleToDraw(
        fixture.bundle, true, &draw ) == render::render_error_t::OK );
    CHECK( draw.pipeline.value == fixture.bundle.mirroredPipeline.value );
    CHECK( draw.uniformBuffers[2].value == fixture.bundle.materialUniformBuffer.value );

    REQUIRE( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
        render::render_error_t::OK );
    CHECK( fixture.pipelineDestroys == 2u );
    CHECK( fixture.shaderDestroys == 1u );
    CHECK( fixture.bufferDestroys == 1u );
    CHECK( fixture.bundle.materialUniformBuffer.value == 0u );
    CHECK( fixture.bundle.materialUniformSlot == render::R_MAX_UNIFORM_BLOCK_BINDINGS );
}

TEST_CASE( "cooked tint preflights its exact VALUE and Material block convention",
    "[CypherRender][MaterialBridge][Tint]" )
{
    SECTION( "cutout Model block carries a std140 cutoff after its matrix" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 0u );
        fixture.UseTint( Text( "tint" ),
            common::render_shader_value_type_t::F32X4, 16u, 80u );
        auto description = fixture.TintDescription();
        description.uniformBlocks[1].byteSize = 80u;
        const auto result = render::R_CreateCookedMaterialBundle(
            description, &fixture.bundle );
        REQUIRE( result.status == render::render_material_bridge_status_t::OK );
        CHECK( fixture.pipelineCreates == 2u );
        CHECK( render::R_IsBufferValid( fixture.bundle.materialUniformBuffer ) );
    }
    SECTION( "lit Model block can carry a padded normal matrix" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 0u );
        fixture.UseTint( Text( "tint" ),
            common::render_shader_value_type_t::F32X4, 16u, 112u );
        auto description = fixture.TintDescription();
        description.uniformBlocks[1].byteSize = 112u;
        const auto result = render::R_CreateCookedMaterialBundle(
            description, &fixture.bundle );
        REQUIRE( result.status == render::render_material_bridge_status_t::OK );
        CHECK( fixture.pipelineCreates == 2u );
        CHECK( render::R_IsBufferValid( fixture.bundle.materialUniformBuffer ) );
    }
    SECTION( "lit cutout Model block carries a normal matrix and cutoff" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 0u );
        fixture.UseTint( Text( "tint" ),
            common::render_shader_value_type_t::F32X4, 16u, 128u );
        auto description = fixture.TintDescription();
        description.uniformBlocks[1].byteSize = 128u;
        const auto result = render::R_CreateCookedMaterialBundle(
            description, &fixture.bundle );
        REQUIRE( result.status == render::render_material_bridge_status_t::OK );
        CHECK( fixture.pipelineCreates == 2u );
        CHECK( render::R_IsBufferValid( fixture.bundle.materialUniformBuffer ) );
    }
    SECTION( "unsupported Model block sizes fail before GPU creation" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 0u );
        fixture.UseTint();
        auto description = fixture.TintDescription();
        description.uniformBlocks[1].byteSize = 96u;
        const auto result = render::R_CreateCookedMaterialBundle(
            description, &fixture.bundle );
        CHECK( result.status ==
            render::render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.bufferCreates == 0u );
        CHECK( fixture.pipelineCreates == 0u );
    }
    SECTION( "declared cutout size must match the cooked shader binding" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 0u );
        fixture.UseTint();
        auto description = fixture.TintDescription();
        description.uniformBlocks[1].byteSize = 80u;
        const auto result = render::R_CreateCookedMaterialBundle(
            description, &fixture.bundle );
        CHECK( result.status ==
            render::render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.bufferCreates == 0u );
        CHECK( fixture.pipelineCreates == 0u );
    }
    SECTION( "mismatched VALUE name" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 0u );
        fixture.UseTint( Text( "otherTint" ) );
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.TintDescription(), &fixture.bundle );
        CHECK( result.status ==
            render::render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.bufferCreates == 0u );
        CHECK( fixture.pipelineCreates == 0u );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
    }
    SECTION( "mismatched VALUE type" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 0u );
        fixture.UseTint( Text( "tint" ), common::render_shader_value_type_t::F32X3 );
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.TintDescription(), &fixture.bundle );
        CHECK( result.status ==
            render::render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.bufferCreates == 0u );
        CHECK( fixture.pipelineCreates == 0u );
    }
    SECTION( "mismatched Material block size" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 0u );
        fixture.UseTint( Text( "tint" ),
            common::render_shader_value_type_t::F32X4, 32u );
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.TintDescription(), &fixture.bundle );
        CHECK( result.status ==
            render::render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.bufferCreates == 0u );
        CHECK( fixture.pipelineCreates == 0u );
    }
    SECTION( "Material block must be declared at slot two" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 0u );
        fixture.UseTint();
        auto description = fixture.TintDescription();
        description.uniformBlocks[2].name = "Other";
        const auto result = render::R_CreateCookedMaterialBundle(
            description, &fixture.bundle );
        CHECK( result.status ==
            render::render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.bufferCreates == 0u );
        CHECK( fixture.pipelineCreates == 0u );
    }
}

TEST_CASE( "cooked tint buffer failure rolls back and destroy failure remains retryable",
    "[CypherRender][MaterialBridge][Tint]" )
{
    SECTION( "failed native buffer creation retires returned token and shader" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 0u );
        fixture.UseTint();
        fixture.failBufferCreate = true;
        fixture.tokenOnBufferFailure = true;
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.TintDescription(), &fixture.bundle );
        CHECK( result.status == render::render_material_bridge_status_t::RENDER_ERROR );
        CHECK( result.renderError == render::render_error_t::ERR_OUT_OF_MEMORY );
        CHECK( fixture.shaderCreates == 1u );
        CHECK( fixture.shaderDestroys == 1u );
        CHECK( fixture.bufferCreates == 1u );
        CHECK( fixture.bufferDestroys == 1u );
        CHECK( fixture.pipelineCreates == 0u );
        CHECK( fixture.bundle.materialUniformBuffer.value == 0u );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
    }
    SECTION( "failed pipeline creation retires the material uniform" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 0u );
        fixture.UseTint();
        fixture.pipelineResult = render::render_error_t::ERR_OUT_OF_MEMORY;
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.TintDescription(), &fixture.bundle );
        CHECK( result.status == render::render_material_bridge_status_t::RENDER_ERROR );
        CHECK( result.renderError == render::render_error_t::ERR_OUT_OF_MEMORY );
        CHECK( fixture.shaderCreates == 1u );
        CHECK( fixture.shaderDestroys == 1u );
        CHECK( fixture.bufferCreates == 1u );
        CHECK( fixture.bufferDestroys == 1u );
        CHECK( fixture.pipelineCreates == 1u );
        CHECK( fixture.bundle.materialUniformBuffer.value == 0u );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
    }
    SECTION( "rollback failure publishes the surviving material uniform" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 0u );
        fixture.UseTint();
        fixture.pipelineResult = render::render_error_t::ERR_OUT_OF_MEMORY;
        fixture.failNextBufferDestroy = true;
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.TintDescription(), &fixture.bundle );
        CHECK( result.status == render::render_material_bridge_status_t::CLEANUP_FAILED );
        CHECK( result.cleanupRenderError == render::render_error_t::ERR_DEVICE_LOST );
        CHECK( fixture.bundle.shader.value == 0u );
        CHECK( fixture.bundle.pipeline.value == 0u );
        CHECK( render::R_IsBufferValid( fixture.bundle.materialUniformBuffer ) );
        CHECK( fixture.bundle.materialUniformSlot == 2u );
        CHECK( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
            render::render_error_t::OK );
        CHECK( fixture.bufferDestroys == 1u );
        CHECK( fixture.bundle.materialUniformBuffer.value == 0u );
    }
    SECTION( "destroy failure retains live uniform buffer for retry" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 0u );
        fixture.UseTint();
        REQUIRE( render::R_CreateCookedMaterialBundle(
            fixture.TintDescription(), &fixture.bundle ).status ==
            render::render_material_bridge_status_t::OK );
        fixture.failNextBufferDestroy = true;
        CHECK( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
            render::render_error_t::ERR_DEVICE_LOST );
        CHECK( render::R_IsBufferValid( fixture.bundle.materialUniformBuffer ) );
        CHECK( fixture.bufferDestroys == 0u );
        CHECK( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
            render::render_error_t::OK );
        CHECK( fixture.bufferDestroys == 1u );
        CHECK( fixture.bundle.materialUniformBuffer.value == 0u );
    }
}

TEST_CASE( "cooked material bridge adopts linear float albedo with a single level or authored mips",
    "[CypherRender][MaterialBridge][HDR]" )
{
    for ( const bool authoredMips : { false, true } ) {
        bridge_fixture_t fixture{};
        fixture.Init( 2u );
        fixture.UseFloatAlbedo( authoredMips );
        common::cooked_texture_view_t floatView{};
        REQUIRE( common::CookedTexture_Succeeded( common::CookedTexture_Read(
            { fixture.cooked.albedo.data(), fixture.cooked.albedo.size() },
            &floatView ) ) );
        REQUIRE( floatView.nMipLevels == ( authoredMips ? 2u : 1u ) );

        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        REQUIRE( result.status == render::render_material_bridge_status_t::OK );
        CHECK( fixture.shaderCreates == 1u );
        CHECK( fixture.textureCreates == 2u );
        CHECK( fixture.pipelineCreates == 2u );
        CHECK( fixture.bundle.textureCount == 2u );
        CHECK( fixture.uploadedFormats[0] == render::render_texture_format_t::RGBA32F );
        CHECK_FALSE( fixture.uploadedSrgb[0] );
        CHECK( fixture.uploadedLevelCount[0] == floatView.nMipLevels );
        for ( common::u32 level = 0u; level < floatView.nMipLevels; ++level ) {
            CHECK( fixture.uploadedLevelSizes[0][level] ==
                floatView.mips[level].pixels.cbSize );
            CHECK( std::memcmp( fixture.uploadedLevelPixels[0][level],
                floatView.mips[level].pixels.pData,
                floatView.mips[level].pixels.cbSize ) == 0 );
        }
        CHECK( fixture.uploadedFormats[1] == render::render_texture_format_t::RGBA8 );
        CHECK( fixture.uploadedPixels[1][0] == 0x13u );
        render::render_texture_info_t textureInfo{};
        REQUIRE( render::R_GetTextureInfo( fixture.bundle.textures[0],
            &textureInfo ) == render::render_error_t::OK );
        CHECK( textureInfo.format == render::render_texture_format_t::RGBA32F );
        CHECK( textureInfo.mipLevels == floatView.nMipLevels );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );

        std::memset( fixture.cooked.albedo.data(), 0, fixture.cooked.albedo.size() );
        render::render_draw_indexed_desc_t draw{};
        REQUIRE( render::R_ApplyCookedMaterialBundleToDraw(
            fixture.bundle, &draw ) == render::render_error_t::OK );
        CHECK( draw.sampledTextures[0].value == fixture.bundle.textures[0].value );
        CHECK( draw.sampledTextures[1].value == fixture.bundle.textures[1].value );
        REQUIRE( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
            render::render_error_t::OK );
        CHECK( fixture.shaderDestroys == 1u );
        CHECK( fixture.textureDestroys == 2u );
        CHECK( fixture.pipelineDestroys == 2u );
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
    CHECK( fixture.lastPipelineBlendMode == render::render_blend_mode_t::STRAIGHT_ALPHA );
    CHECK_FALSE( fixture.lastPipelineDepthWrite );
    render::render_pipeline_info_t pipelineInfo{};
    REQUIRE( render::R_GetGraphicsPipelineInfo( fixture.bundle.pipeline,
        &pipelineInfo ) == render::render_error_t::OK );
    CHECK( pipelineInfo.depthTest );
    CHECK_FALSE( pipelineInfo.depthWrite );
    CHECK( pipelineInfo.blendMode == render::render_blend_mode_t::STRAIGHT_ALPHA );
    CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
    std::memset( fixture.cooked.material.data(), 0, fixture.cooked.material.size() );
    CHECK( fixture.bundle.alphaMode == common::render_material_alpha_mode_t::BLEND );
    REQUIRE( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
        render::render_error_t::OK );
    CHECK( fixture.shaderDestroys == 1u );
    CHECK( fixture.pipelineDestroys == 2u );
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
        CHECK( fixture.pipelineCreates == 2u );
        CHECK( fixture.lastPipelineBlendMode == render::render_blend_mode_t::DISABLED );
        CHECK( fixture.lastPipelineDepthWrite );
        render::render_pipeline_info_t pipelineInfo{};
        REQUIRE( render::R_GetGraphicsPipelineInfo( fixture.bundle.pipeline,
            &pipelineInfo ) == render::render_error_t::OK );
        CHECK( pipelineInfo.depthTest );
        CHECK( pipelineInfo.depthWrite );
        CHECK( pipelineInfo.blendMode == render::render_blend_mode_t::DISABLED );
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
        CHECK( fixture.pipelineDestroys == 2u );
        CHECK( fixture.shaderDestroys == 1u );
        CHECK( fixture.textureDestroys == 1u );
        CHECK( fixture.bundle.alphaMode == common::render_material_alpha_mode_t::OPAQUE );
        CHECK( fixture.bundle.alphaCutoff == 0.5F );
    }
}

TEST_CASE( "cooked additive material adopts weighted RGB addition without depth writes",
    "[CypherRender][MaterialBridge]" )
{
    bridge_fixture_t fixture{};
    fixture.Init( 1u, false, false, false,
        common::render_material_alpha_mode_t::ADDITIVE );
    const auto result = render::R_CreateCookedMaterialBundle(
        fixture.Description(), &fixture.bundle );
    REQUIRE( result.status == render::render_material_bridge_status_t::OK );
    CHECK( fixture.bundle.alphaMode == common::render_material_alpha_mode_t::ADDITIVE );
    CHECK( fixture.bundle.alphaCutoff == 0.5F );
    CHECK( fixture.lastPipelineBlendMode == render::render_blend_mode_t::ADDITIVE );
    CHECK_FALSE( fixture.lastPipelineDepthWrite );
    render::render_pipeline_info_t pipelineInfo{};
    REQUIRE( render::R_GetGraphicsPipelineInfo( fixture.bundle.pipeline,
        &pipelineInfo ) == render::render_error_t::OK );
    CHECK( pipelineInfo.depthTest );
    CHECK_FALSE( pipelineInfo.depthWrite );
    CHECK( pipelineInfo.blendMode == render::render_blend_mode_t::ADDITIVE );
    CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );

    std::memset( fixture.cooked.material.data(), 0, fixture.cooked.material.size() );
    CHECK( fixture.bundle.alphaMode == common::render_material_alpha_mode_t::ADDITIVE );
    render::render_draw_indexed_desc_t draw{};
    REQUIRE( render::R_ApplyCookedMaterialBundleToDraw(
        fixture.bundle, &draw ) == render::render_error_t::OK );
    CHECK( draw.pipeline.value == fixture.bundle.pipeline.value );
    CHECK( draw.sampledTextures[0].value == fixture.bundle.textures[0].value );
    REQUIRE( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
        render::render_error_t::OK );
    CHECK( fixture.shaderDestroys == 1u );
    CHECK( fixture.pipelineDestroys == 2u );
    CHECK( fixture.textureDestroys == 1u );
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
    SECTION( "additive feature variants are not silently dropped" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u, false, true, false,
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
    SECTION( "float data texture is not silently bound as a color texture" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u );
        fixture.UseFloatAlbedo( false, common::render_texture_usage_t::DATA );
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        CHECK( result.status == render::render_material_bridge_status_t::MATERIAL_UNSUPPORTED );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.textureCreates == 0u );
        CHECK( fixture.pipelineCreates == 0u );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
    }
    SECTION( "streamed float mip tail has no material adoption path" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u );
        fixture.UseFloatAlbedo( true, common::render_texture_usage_t::COLOR,
            common::cooked_texture_residency_t::MIP_STREAMED );
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        CHECK( result.status == render::render_material_bridge_status_t::MATERIAL_UNSUPPORTED );
        CHECK( fixture.shaderCreates == 0u );
        CHECK( fixture.textureCreates == 0u );
        CHECK( fixture.pipelineCreates == 0u );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
    }
    SECTION( "incomplete authored mip chains are rejected before GPU creation" ) {
        for ( const bool floating : { false, true } ) {
            CAPTURE( floating );
            bridge_fixture_t fixture{};
            fixture.Init( 1u );
            fixture.cooked.MakeIncompleteMipTexture(
                fixture.cooked.albedo, floating );
            fixture.storage.files[2].contents = {
                fixture.cooked.albedo.data(), fixture.cooked.albedo.size()
            };
            common::cooked_texture_view_t view{};
            REQUIRE( common::CookedTexture_Succeeded( common::CookedTexture_Read(
                fixture.storage.files[2].contents, &view ) ) );
            REQUIRE( view.nMipLevels == 2u );
            REQUIRE( common::CookedTexture_FullMipCount(
                view.desc.nWidth, view.desc.nHeight ) == 3u );

            const auto result = render::R_CreateCookedMaterialBundle(
                fixture.Description(), &fixture.bundle );
            CHECK( result.status ==
                render::render_material_bridge_status_t::MATERIAL_UNSUPPORTED );
            CHECK( fixture.shaderCreates == 0u );
            CHECK( fixture.textureCreates == 0u );
            CHECK( fixture.pipelineCreates == 0u );
            CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
        }
    }
}

TEST_CASE( "mirrored pipeline creation and destruction preserve retryable ownership",
    "[CypherRender][MaterialBridge][Mirror]" )
{
    SECTION( "second pipeline failure retires its returned token and the first pipeline" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u );
        fixture.failPipelineAt = 2u;
        fixture.tokenOnPipelineFailure = true;
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        CHECK( result.status == render::render_material_bridge_status_t::RENDER_ERROR );
        CHECK( result.renderError == render::render_error_t::ERR_OUT_OF_MEMORY );
        CHECK( fixture.pipelineCreates == 2u );
        CHECK( fixture.pipelineDestroys == 2u );
        CHECK( fixture.shaderDestroys == 1u );
        CHECK( fixture.textureDestroys == 1u );
        CHECK( fixture.bundle.pipeline.value == 0u );
        CHECK( fixture.bundle.mirroredPipeline.value == 0u );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
    }
    SECTION( "failed cleanup after second create publishes first pipeline for retry" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u );
        fixture.failPipelineAt = 2u;
        fixture.failNextPipelineDestroy = true;
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        CHECK( result.status == render::render_material_bridge_status_t::CLEANUP_FAILED );
        CHECK( result.renderError == render::render_error_t::ERR_OUT_OF_MEMORY );
        CHECK( result.cleanupRenderError == render::render_error_t::ERR_DEVICE_LOST );
        CHECK( fixture.pipelineCreates == 2u );
        CHECK( fixture.pipelineDestroys == 0u );
        CHECK( fixture.shaderDestroys == 0u );
        CHECK( render::R_IsGraphicsPipelineValid( fixture.bundle.pipeline ) );
        CHECK( fixture.bundle.mirroredPipeline.value == 0u );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
        REQUIRE( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
            render::render_error_t::OK );
        CHECK( fixture.pipelineDestroys == 1u );
        CHECK( fixture.shaderDestroys == 1u );
        CHECK( fixture.bundle.pipeline.value == 0u );
    }
    SECTION( "failed mirror destruction retains shader until retry" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u );
        REQUIRE( render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle ).status ==
            render::render_material_bridge_status_t::OK );
        fixture.failNextPipelineDestroy = true;
        CHECK( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
            render::render_error_t::ERR_DEVICE_LOST );
        CHECK( fixture.pipelineDestroys == 1u );
        CHECK( fixture.shaderDestroys == 0u );
        CHECK( fixture.bundle.pipeline.value == 0u );
        CHECK( render::R_IsGraphicsPipelineValid( fixture.bundle.mirroredPipeline ) );
        REQUIRE( render::R_DestroyCookedMaterialBundle( &fixture.bundle ) ==
            render::render_error_t::OK );
        CHECK( fixture.pipelineDestroys == 2u );
        CHECK( fixture.shaderDestroys == 1u );
        CHECK( fixture.bundle.mirroredPipeline.value == 0u );
    }
}

TEST_CASE( "cooked material bridge rolls back partially created GPU bundles",
    "[CypherRender][MaterialBridge]" )
{
    SECTION( "failed float mip upload destroys a returned token and shader" ) {
        bridge_fixture_t fixture{};
        fixture.Init( 1u );
        fixture.UseFloatAlbedo( true );
        fixture.failTextureAt = 1u;
        fixture.tokenOnTextureFailure = true;
        const auto result = render::R_CreateCookedMaterialBundle(
            fixture.Description(), &fixture.bundle );
        CHECK( result.status == render::render_material_bridge_status_t::RENDER_ERROR );
        CHECK( result.renderError == render::render_error_t::ERR_OUT_OF_MEMORY );
        CHECK( fixture.shaderCreates == 1u );
        CHECK( fixture.shaderDestroys == 1u );
        CHECK( fixture.textureCreates == 1u );
        CHECK( fixture.textureDestroys == 1u );
        CHECK( fixture.pipelineCreates == 0u );
        CHECK( fixture.bundle.textureCount == 0u );
        CHECK( resource::Res_GetStats( &fixture.resources ).cLiveResources == 0u );
    }
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
